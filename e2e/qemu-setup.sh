#!/bin/bash
# QEMU Testing Environment Setup Script
# This script downloads and configures a QEMU VM for safe kernel module testing

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QEMU_DIR="$SCRIPT_DIR/qemu-env"
UBUNTU_VERSION="24.04"
IMAGE_URL="https://cloud-images.ubuntu.com/releases/${UBUNTU_VERSION}/release/ubuntu-${UBUNTU_VERSION}-server-cloudimg-amd64.img"
HOST_KERNEL_RELEASE="$(uname -r)"
INSTANCE_ID_SUFFIX="${HOST_KERNEL_RELEASE//[^a-zA-Z0-9-]/-}"
INSTANCE_ID="kernel-test-vm-${INSTANCE_ID_SUFFIX}"

echo "==================================================="
echo "QEMU Testing Environment Setup"
echo "==================================================="
echo "Host kernel release detected: ${HOST_KERNEL_RELEASE}"

# Attempt to install QEMU automatically if missing
install_qemu_if_missing() {
  echo "QEMU not found; attempting automatic installation..."

  # Linux (Debian/Ubuntu)
  if command -v apt-get &> /dev/null; then
    echo "Detected apt-get; installing qemu + utilities..."
    sudo apt-get update -y || sudo apt-get update
    # Prefer cloud-image-utils (provides cloud-localds); also add genisoimage as fallback
    sudo apt-get install -y qemu-system-x86 qemu-utils cloud-image-utils genisoimage || \
      sudo apt-get install -y qemu-system-x86 qemu-utils genisoimage
    return
  fi

  # Linux (Fedora/RHEL)
  if command -v dnf &> /dev/null; then
    echo "Detected dnf; installing qemu + utilities..."
    sudo dnf install -y qemu-system-x86 qemu-img cloud-utils genisoimage || \
      sudo dnf install -y qemu-system-x86 qemu-img genisoimage
    return
  fi

  # macOS (Homebrew)
  if [[ "$OSTYPE" == "darwin"* ]]; then
    if ! command -v brew &> /dev/null; then
      echo "ERROR: Homebrew not found. Install Homebrew from https://brew.sh and re-run."
      exit 1
    fi
    echo "Detected macOS; installing qemu via Homebrew..."
    brew update
    brew install qemu || brew upgrade qemu || true
    # genisoimage isn't standard on macOS; mkisofs is provided by cdrtools
    brew install cdrtools || true
    return
  fi

  # Arch Linux (optional)
  if command -v pacman &> /dev/null; then
    echo "Detected pacman; installing qemu + utilities..."
    sudo pacman -Syu --noconfirm qemu qemu-img || sudo pacman -Sy --noconfirm qemu
    # cloud-image-utils may not exist; ensure an ISO tool is present
    sudo pacman -Sy --noconfirm cdrtools || true
    return
  fi

  echo "ERROR: Unsupported or undetected package manager. Please install QEMU manually."
  echo "Hint: Ubuntu/Debian: apt-get install qemu-system-x86 qemu-utils cloud-image-utils"
  echo "      Fedora: dnf install qemu-system-x86 qemu-img cloud-utils"
  echo "      macOS: brew install qemu"
  exit 1
}

# Check if QEMU is installed
if ! command -v qemu-system-x86_64 &> /dev/null; then
  install_qemu_if_missing
  # Re-check after installation attempt
  if ! command -v qemu-system-x86_64 &> /dev/null; then
    echo "ERROR: QEMU installation failed or qemu-system-x86_64 still not found."
    exit 1
  fi
fi

# Create QEMU directory
mkdir -p "$QEMU_DIR"
cd "$QEMU_DIR"

# Persist host kernel target for debugging and run-time visibility
printf '%s\n' "$HOST_KERNEL_RELEASE" > host-kernel-release

# Download Ubuntu cloud image if not exists
if [ ! -f "ubuntu-${UBUNTU_VERSION}.img" ]; then
    echo "Downloading Ubuntu ${UBUNTU_VERSION} cloud image..."
    wget -O "ubuntu-${UBUNTU_VERSION}.img" "$IMAGE_URL"
    echo "Resizing image to 20GB..."
    qemu-img resize "ubuntu-${UBUNTU_VERSION}.img" 20G
else
    echo "Ubuntu image already exists, skipping download..."
fi

# Create cloud-init user-data
echo "Creating cloud-init configuration..."

# Detect SSH public key for passwordless access
SSH_KEY=""
if [ -f ~/.ssh/id_rsa.pub ]; then
  SSH_KEY=$(cat ~/.ssh/id_rsa.pub)
elif [ -f ~/.ssh/id_ed25519.pub ]; then
  SSH_KEY=$(cat ~/.ssh/id_ed25519.pub)
fi

# Generate user-data with optional SSH key
if [ -n "$SSH_KEY" ]; then
  echo "  - Found SSH public key, enabling key-based authentication"
  cat > user-data.yaml << EOF
#cloud-config
hostname: kernel-test-vm
manage_etc_hosts: true

users:
  - name: ubuntu
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    lock_passwd: false
    # Default password: ubuntu (for console/fallback)
    passwd: \$6\$rounds=4096\$saltsalt\$IjcDQqnK7H8e4Bm5nLz0.ZJNnPkF/0Qd.2Y9xNJ9R0B9dAr1e5E4zJ2Q3B6Q8Q9B3B6Q8Q9B3B6Q8Q9B3B6Q8Q.
    ssh_authorized_keys:
      - $SSH_KEY
EOF
else
  echo "  - No SSH key found, using password-only authentication"
  cat > user-data.yaml << 'EOF'
#cloud-config
hostname: kernel-test-vm
manage_etc_hosts: true

users:
  - name: ubuntu
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    lock_passwd: false
    # Default password: ubuntu (change after first login)
    passwd: $6$rounds=4096$saltsalt$IjcDQqnK7H8e4Bm5nLz0.ZJNnPkF/0Qd.2Y9xNJ9R0B9dAr1e5E4zJ2Q3B6Q8Q9B3B6Q8Q9B3B6Q8Q9B3B6Q8Q.
EOF
fi
cat >> user-data.yaml << 'EOF'

runcmd:
  - echo "ubuntu:ubuntu" | chpasswd
  - |
    set -e
    export DEBIAN_FRONTEND=noninteractive

    # Recover from interrupted package operations if cloud-init/apt left dpkg dirty.
    rm -f /var/lib/dpkg/updates/* || true
    dpkg --configure -a || true

    apt-get update
    apt-get install -y --reinstall make build-essential kmod git vim net-tools

    # Ensure make is healthy; some broken images can contain a zero-byte binary.
    if [ ! -x "$(command -v make)" ] || [ ! -s "$(command -v make)" ]; then
      apt-get install -y --reinstall make build-essential
    fi
EOF

cat >> user-data.yaml << EOF
  - |
    set -e
    export DEBIAN_FRONTEND=noninteractive
    rm -f /var/lib/dpkg/updates/* || true
    dpkg --configure -a || true
    if ! apt-get install -y linux-image-${HOST_KERNEL_RELEASE} linux-headers-${HOST_KERNEL_RELEASE}; then
      echo "Failed to install host-matching kernel packages: ${HOST_KERNEL_RELEASE}" >&2
      exit 1
    fi
  - update-grub || true
  - echo "${HOST_KERNEL_RELEASE}" > /etc/host-kernel-release-target
EOF

cat >> user-data.yaml << 'EOF'
  - systemctl enable serial-getty@ttyS0.service
  - systemctl start serial-getty@ttyS0.service

write_files:
  - path: /etc/ssh/sshd_config.d/90-testing.conf
    content: |
      PermitRootLogin no
      PasswordAuthentication yes
      PubkeyAuthentication yes

power_state:
  mode: reboot
  timeout: 300
  condition: True
EOF

# Create meta-data file (required by cloud-init)
echo "Creating cloud-init meta-data..."
cat > meta-data << EOF
instance-id: ${INSTANCE_ID}
local-hostname: kernel-test-vm
EOF

# Generate cloud-init ISO
echo "Generating cloud-init seed image..."
if command -v cloud-localds &> /dev/null; then
    cloud-localds seed.img user-data.yaml meta-data
elif command -v genisoimage &> /dev/null; then
    # Fallback method - create ISO with both user-data and meta-data
    genisoimage -output seed.img -volid cidata -joliet -rock user-data.yaml meta-data
else
    echo "WARNING: cloud-localds not found. You may need to install cloud-image-utils"
    echo "Creating minimal seed image..."
    # Create a minimal ISO with user-data and meta-data
    mkdir -p seed
    cp user-data.yaml meta-data seed/
    genisoimage -output seed.img -volid cidata -joliet -rock seed/ 2>/dev/null || \
        echo "ERROR: Could not create seed image. Please install cloud-image-utils or genisoimage"
fi

echo ""
echo "==================================================="
echo "Setup Complete!"
echo "==================================================="
echo ""
echo "VM Details:"
echo "  - OS: Ubuntu ${UBUNTU_VERSION}"
echo "  - Target kernel release: ${HOST_KERNEL_RELEASE}"
echo "  - Username: ubuntu"
if [ -n "$SSH_KEY" ]; then
echo "  - SSH: Key-based authentication enabled"
echo "  - Password: ubuntu (for console/fallback)"
else
echo "  - SSH: Password authentication only"
echo "  - Password: ubuntu"
fi
echo "  - SSH Port: 2222 (forwarded from host)"
echo ""
echo "Next steps:"
echo "  1. Run: sudo ./e2e/qemu-run.sh"
if [ -n "$SSH_KEY" ]; then
echo "  2. SSH without password: ssh -p 2222 ubuntu@localhost"
else
echo "  2. Login with username 'ubuntu' and password 'ubuntu'"
fi
echo "  3. Copy your module and test it safely!"
echo ""
echo "Note: First boot may take 1-2 minutes for cloud-init setup"
echo "==================================================="

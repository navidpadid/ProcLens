# Scripts Documentation

Scripts for testing ProcLens in isolated QEMU virtual machines.

## Quick Reference

```bash
# QEMU Testing
./e2e/qemu-setup.sh      # One-time setup
./e2e/qemu-run.sh         # Start VM
./e2e/qemu-test.sh        # Automated tests (run from host in another terminal)

# Other utilities
./.github/pre-commit.sh       # Pre-commit hook (auto-installed in dev container)
./e2e/quick-reference.sh      # Display quick reference guide
```

## Script Descriptions

### `qemu-setup.sh`
**Purpose**: One-time setup of QEMU testing environment

**What it does**:
- Downloads Ubuntu 24.04 cloud image (~700MB)
- Creates cloud-init configuration for VM
- Sets up VM with kernel headers pre-installed
- Configures SSH access with default credentials

**Requirements**:
- `qemu-system-x86_64`
- `qemu-img`
- `cloud-localds` (from cloud-image-utils package)

**Output**: Creates `qemu-env/` directory with VM image and cloud-init config

### `qemu-run.sh`
**Purpose**: Start the QEMU virtual machine

**What it does**:
- Starts QEMU VM with appropriate settings
- Forwards SSH port 2222 to VM's port 22
- Allocates 2GB RAM and 2 CPU cores
- Enables KVM acceleration if available
- Provides serial console access

**VM Access**:
- SSH: `ssh -p 2222 ubuntu@localhost`
- Default password: `ubuntu`
- Exit console: Press Ctrl+A then X

**Note**: First boot takes 1-2 minutes for cloud-init to complete.

### `qemu-test.sh`
**Purpose**: Automated end-to-end testing in QEMU VM

**What it does**:
1. Syncs project files to VM
2. Builds kernel module and user program in VM
3. Loads kernel module
4. Builds the multi-threaded test program
5. Runs user program with test process
6. Captures and displays output
7. Unloads kernel module
8. Cleans up

**Requirements**:
- QEMU VM must be running (`qemu-run.sh`)
- SSH access configured (done by `qemu-setup.sh`)

**Usage**:
```bash
# Start VM in one terminal
./e2e/qemu-run.sh

# Run tests from another terminal
./e2e/qemu-test.sh
```

**Output**: Shows build process, module loading, test results, and kernel logs.

### `.github/pre-commit.sh`
**Purpose**: Git pre-commit hook for code quality

**What it does**:
- Runs code formatting checks
- Executes cppcheck static analysis
- Validates kernel coding style with checkpatch

**Installation**: Automatically installed in dev container on startup.

**Manual bypass** (use sparingly):
```bash
git commit --no-verify -m "message"
```

### `quick-reference.sh`
**Purpose**: Display comprehensive quick reference guide

**What it shows**:
- Local testing commands
- QEMU setup and usage
- Build targets
- Memory information extracted
- Important limitations
- Troubleshooting tips

**Usage**:
```bash
./e2e/quick-reference.sh
```

## QEMU VM Details

### VM Specifications
- **OS**: Ubuntu 24.04 LTS
- **Kernel**: 6.8+
- **RAM**: 2GB
- **CPUs**: 2 cores
- **Disk**: 10GB (cloud image)
- **Network**: User-mode networking with port forwarding

### Default Credentials
- **Username**: `ubuntu`
- **Password**: `ubuntu`
- **Note**: Change password after first login for security

### Port Forwarding
- Host port 2222 → VM port 22 (SSH)

### SSH Key Setup (Optional)
To avoid password prompts:

```bash
# Generate SSH key if you don't have one
ssh-keygen -t ed25519

# Copy to VM
ssh-copy-id -p 2222 ubuntu@localhost
```

### File Transfer

**Copy TO VM**:
```bash
scp -P 2222 localfile.txt ubuntu@localhost:~/
```

**Copy FROM VM**:
```bash
scp -P 2222 ubuntu@localhost:~/remotefile.txt ./
```

**Sync directory** (used by qemu-test.sh):
```bash
rsync -avz --exclude='.git' --exclude='build' \
  -e "ssh -p 2222 -o StrictHostKeyChecking=no" \
  ./ ubuntu@localhost:~/kernel_module/
```

## Cleanup and Maintenance

### Remove QEMU Environment
```bash
rm -rf e2e/qemu-env/
```

### Reinstall Fresh VM
```bash
rm -rf e2e/qemu-env/
./e2e/qemu-setup.sh
```

### Check VM Status
```bash
# Check if QEMU is running
ps aux | grep qemu

# Test SSH connection
ssh -p 2222 ubuntu@localhost echo "VM is accessible"
```

## Troubleshooting

### Cannot connect to VM
1. Check if VM is running: `ps aux | grep qemu`
2. Wait for cloud-init to complete (1-2 minutes on first boot)
3. Verify port forwarding: `netstat -ln | grep 2222`
4. Check VM console for errors (Ctrl+A then C in qemu-run.sh)

### VM is slow
1. Ensure KVM acceleration is enabled: `ls /dev/kvm`
2. Check CPU/memory allocation in `qemu-run.sh`
3. Verify host system has sufficient resources

### Build fails in VM
1. Check kernel headers: `ls /lib/modules/$(uname -r)/build`
2. Update VM packages: `sudo apt update && sudo apt upgrade`
3. Verify project files synced correctly

### Module won't load
1. Check kernel logs: `sudo dmesg | tail -20`
2. Verify kernel version compatibility
3. Ensure no conflicting modules loaded

## Benefits of QEMU Testing

- **Safe**: Kernel panics won't crash your host system  
- **Isolated**: No risk to host kernel or data  
- **Reproducible**: Clean VM state for each test  
- **Fast**: Quick VM reset and rebuild  
- **Automated**: Full CI/CD integration possible  
- **Realistic**: Tests in real Linux kernel environment  

## Alternative Testing Options

1. **Dev Container** (current setup) - Isolated from host
2. **QEMU VM** (recommended) - Extra safety from kernel crashes
3. **Cloud VM** - Disposable testing environment
4. **Physical test machine** - Dedicated hardware for kernel development

For kernel module development, QEMU offers the best balance of safety, speed, and realism.

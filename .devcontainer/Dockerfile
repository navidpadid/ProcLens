# Use Ubuntu 24.04 as base image (includes kernel 6.8+ headers)
FROM ubuntu:24.04

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Set up timezone
RUN ln -fs /usr/share/zoneinfo/UTC /etc/localtime

# Install essential tools and kernel headers
RUN apt-get update && apt-get install -y \
    build-essential \
    linux-headers-generic \
    kmod \
    git \
    vim \
    sudo \
    curl \
    wget \
    ca-certificates \
    gnupg \
    lsb-release \
    gcc \
    make \
    bc \
    libelf-dev \
    libssl-dev \
    clang-format \
    sparse \
    cppcheck \
    && rm -rf /var/lib/apt/lists/*

# Ensure ubuntu user exists and has sudo privileges
RUN if ! id -u ubuntu > /dev/null 2>&1; then \
        useradd -m -s /bin/bash ubuntu; \
    fi && \
    echo "ubuntu ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# Set working directory
WORKDIR /workspaces/ProcLens

# Switch to ubuntu user
USER ubuntu

# Set environment
ENV HOME=/home/ubuntu
ENV SHELL=/bin/bash

CMD ["/bin/bash"]

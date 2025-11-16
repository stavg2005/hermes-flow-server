# Start from a standard Ubuntu 22.04 image
FROM ubuntu:22.04

# Set environment variables to avoid interactive prompts
ENV TZ=Etc/UTC
ENV DEBIAN_FRONTEND=noninteractive

# 1. Install prerequisites for adding new repositories
RUN apt-get update && apt-get install -y \
    ca-certificates \
    gnupg \
    wget \
    software-properties-common

# 2. Add the official Kitware (CMake) APT repository
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor - | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
RUN echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ jammy main' | tee /etc/apt/sources.list.d/kitware.list >/dev/null

# 3. Add the official LLVM (Clang) repository
RUN wget https://apt.llvm.org/llvm.sh
RUN chmod +x llvm.sh
RUN ./llvm.sh 21


RUN apt-get update && apt-get install -y \
    locales \
    cmake \
    build-essential \
    ninja-build \
    git \
    libssl-dev \
    liburing-dev \
    pkg-config \
    clang-21 \
    clangd-21 \
    clang-format-21 \
    lld-21 \
    libc++-21-dev \
    libc++abi-21-dev \
    && rm -rf /var/lib/apt/lists/*

# 5. Generate and set the en_US.UTF-8 locale
RUN locale-gen en_US.UTF-8
ENV LANG en_US.UTF-8
ENV LANGUAGE en_US:en
ENV LC_ALL en_US.UTF-8

# 6. Set Clang 21 as the default compiler
ENV CC=/usr/bin/clang-21
ENV CXX=/usr/bin/clang++-21

# 7. Set the default working directory
WORKDIR /app

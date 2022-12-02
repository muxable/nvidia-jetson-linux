FROM ubuntu:18.04 AS builder

RUN apt update && apt install -y build-essential bc wget xxd
RUN wget http://releases.linaro.org/components/toolchain/binaries/7.3-2018.05/aarch64-linux-gnu/gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu.tar.xz
RUN tar xf gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu.tar.xz

COPY . /Linux_for_Tegra/source/public

RUN mkdir -p /work/build

RUN cd /Linux_for_Tegra/source/public/kernel/kernel-4.9 && \
    make ARCH=arm64 O=/work/build tegra_defconfig && \
    CROSS_COMPILE=/gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu- LOCALVERSION=-tegra make ARCH=arm64 O=/work/build -j$(nproc)

FROM scratch AS export

COPY --from=builder /work/build /
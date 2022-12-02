FROM ubuntu:18.04 AS build

RUN apt update && apt install -y build-essential bc wget xxd
RUN wget http://releases.linaro.org/components/toolchain/binaries/7.3-2018.05/aarch64-linux-gnu/gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu.tar.xz
RUN tar xf gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu.tar.xz

COPY /sources /work/sources

RUN mkdir -p /work/build

WORKDIR /work/sources/kernel/kernel-4.9

ENV CROSS_COMPILE /gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
ENV LOCALVERSION -tegra

RUN make ARCH=arm64 O=/work/build tegra_defconfig
RUN make ARCH=arm64 O=/work/build -j$(nproc)
RUN make ARCH=arm64 O=/work/build INSTALL_MOD_PATH=/work/modules -j$(nproc) modules_install

FROM scratch AS export

# there are a lot more components in the linux kernel, but these are all we need.
COPY --from=build /work/build/arch/arm64/boot /boot
COPY --from=build /work/modules/lib /modules/lib

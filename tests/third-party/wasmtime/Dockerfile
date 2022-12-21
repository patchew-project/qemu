# SPDX-License-Identifier: GPL-2.0-or-later

# Image for building and running the wasmtime testsuite.

ARG deb_arch
FROM registry.gitlab.com/qemu-project/qemu/qemu/debian-${deb_arch}-cross:latest

RUN export DEBIAN_FRONTEND=noninteractive && \
    eatmydata apt-get update && \
    eatmydata apt-get dist-upgrade -y && \
    eatmydata apt-get install --no-install-recommends -y \
                      curl && \
    eatmydata apt-get autoremove -y && \
    eatmydata apt-get autoclean -y

ARG group_id
ARG group_name
RUN if ! getent group ${group_id} >/dev/null 2>&1; then \
        groupdel ${group_name} >/dev/null 2>&1 ; \
        groupadd --gid=${group_id} ${group_name} ; \
    fi

ARG user_id
ARG user_name
RUN if ! getent user ${user_id} >/dev/null 2>&1; then \
        userdel ${user_name} >/dev/null 2>&1 ; \
        useradd \
            --create-home \
            --gid=${group_id} \
            --uid=${user_id} \
            ${user_name} ; \
    fi

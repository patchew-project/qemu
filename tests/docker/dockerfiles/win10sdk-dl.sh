#!/bin/bash
#
# Install WHPX headers from Windows Software Development Kit
# https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk
#
# SPDX-License-Identifier: GPL-2.0-or-later

WINDIR=/opt/win10sdk
mkdir -p ${WINDIR}
pushd ${WINDIR}
# Get the bundle base for Windows SDK v10.0.18362.1
BASE_URL=$(curl --silent --include 'http://go.microsoft.com/fwlink/?prd=11966&pver=1.0&plcid=0x409&clcid=0x409&ar=Windows10&sar=SDK&o1=10.0.18362.1' | sed -nE 's_Location: (.*)/\r_\1_p')/Installers
# Fetch the MSI containing the headers
wget --no-verbose ${BASE_URL}/'Windows SDK Desktop Headers x86-x86_en-us.msi'
while true; do
    # Fetch all cabinets required by this MSI
    CAB_NAME=$(msiextract Windows\ SDK\ Desktop\ Headers\ x86-x86_en-us.msi 3>&1 2>&3 3>&-| sed -nE "s_.*Error opening file $PWD/(.*): No such file or directory_\1_p")
    test -z "${CAB_NAME}" && break
    wget --no-verbose ${BASE_URL}/${CAB_NAME}
done
rm *.{cab,msi}
mkdir /opt/win10sdk/include
# Only keep the WHPX headers
for inc in "${WINDIR}/Program Files/Windows Kits/10/Include/10.0.18362.0/um"/WinHv*; do
    ln -s "${inc}" /opt/win10sdk/include
done
popd > /dev/null

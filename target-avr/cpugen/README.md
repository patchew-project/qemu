# CPUGEN
## How to build
within ```cpugen``` directory do
```
git clone https://github.com/leethomason/tinyxml2
git clone https://github.com/jbeder/yaml-cpp
mkdir build
cd build
cmake ..
make
```
## How to use
```
cpugen ../cpu/avr.yaml
xsltproc ../xsl/decode.c.xsl output.xml > ../../decode.c
xsltproc ../xsl/translate-inst.h.xsl output.xml > ../../translate-inst.h
```

FROM gcr.io/oss-fuzz-base/base-builder
RUN git clone --depth 1 https://github.com/slightlabs/CxxScript.git cxxscript
WORKDIR cxxscript
COPY build.sh $SRC/

FROM fuzzers/afl:2.52

RUN apt-get update
RUN apt install -y build-essential wget git clang  automake autotools-dev  libtool zlib1g zlib1g-dev libexif-dev     libboost-all-dev libssl-dev
RUN  wget https://github.com/Kitware/CMake/releases/download/v3.20.1/cmake-3.20.1.tar.gz
RUN tar xvfz cmake-3.20.1.tar.gz
WORKDIR /cmake-3.20.1
RUN ./bootstrap
RUN make
RUN make install
WORKDIR /
RUN git clone https://github.com/wasmx/fizzy
RUN cmake -DFIZZY_WASI=ON -DCMAKE_C_COMPILER=afl-clang -DCMAKE_CXX_COMPILER=afl-clang++
RUN make
RUN make install
RUN mkdir /wasmCorpus
RUN wget https://github.com/mdn/webassembly-examples/blob/master/js-api-examples/fail.wasm
RUN wget https://github.com/mdn/webassembly-examples/blob/master/js-api-examples/global.wasm
RUN wget https://github.com/mdn/webassembly-examples/blob/master/js-api-examples/memory.wasm
RUN mv *.wasm /wasmCorpus

ENTRYPOINT ["afl-fuzz", "-i", "/wasmCorpus", "-o", "/fizzyOut"]
CMD ["/fizzy/bin/fizzy-wasi", "@@"]

FROM cochain/eos-builder
ARG version
ARG symbol=EOC
ARG cdt_version=v1.5.0-rc1

COPY . /eos/

RUN cd eos && echo $version > /etc/eosio-version \
    && cmake -H. -B"/opt/eosio" -GNinja -DCMAKE_BUILD_TYPE=Release -DWASM_ROOT=/opt/wasm -DCMAKE_CXX_COMPILER=clang++ \
       -DCMAKE_C_COMPILER=clang -DCMAKE_INSTALL_PREFIX=/opt/eosio -DCORE_SYMBOL_NAME=$symbol \
       -DOPENSSL_ROOT_DIR=/usr/include/openssl -DOPENSSL_INCLUDE_DIR=/usr/include/openssl \
    && cmake --build /opt/eosio --target install \
    && cp /eos/Docker/config.ini / && ln -s /opt/eosio/contracts /contracts && cp /eos/Docker/nodeosd.sh /opt/eosio/bin/nodeosd.sh && ln -s /eos/tutorials /tutorials \
    && rm -rf /eos

RUN deb=$(curl -s https://api.github.com/repos/eoscochain/eoscochain.cdt/releases/tags/$cdt_version | grep "browser_download_url.*deb" | cut -d '"' -f 4) \
    && filename=$(curl -s https://api.github.com/repos/eoscochain/eoscochain.cdt/releases/tags/$cdt_version | grep "name.*deb" | cut -d '"' -f 4) \
    && wget $deb && dpkg -i "$filename" && rm -f "$filename"

RUN git clone https://github.com/eoscochain/eoscochain.contracts \
    && cd eoscochain.contracts && chmod +x build.sh && ./build.sh \
    && mv build /opt/eosio/contracts

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install openssl ca-certificates vim psmisc python3-pip && rm -rf /var/lib/apt/lists/*
RUN pip3 install numpy
ENV EOSIO_ROOT=/opt/eosio
RUN chmod +x /opt/eosio/bin/nodeosd.sh
ENV LD_LIBRARY_PATH /usr/local/lib
VOLUME /opt/eosio/bin/data-dir
ENV PATH /opt/eosio/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

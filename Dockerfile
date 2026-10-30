# Build, test, and run the Mini SQL Query Engine in a clean image.
#   docker build -t minisql .
#   docker run --rm -it minisql                      # interactive REPL
#   docker run --rm minisql minisql_bench            # benchmark
FROM ubuntu:24.04 AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake make python3 ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN rm -rf build build-* \
 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMINISQL_WERROR=ON \
 && cmake --build build -j"$(nproc)" \
 && ctest --test-dir build --output-on-failure

FROM ubuntu:24.04
RUN useradd --create-home minisql
WORKDIR /app
COPY --from=build /src/build/minisql /src/build/minisql_bench /usr/local/bin/
COPY --from=build /src/data ./data
USER minisql
CMD ["minisql"]

# Use Ubuntu as base image
FROM ubuntu:22.04

# Install required packages
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    mpich \
    git \
    net-tools \
    netcat \
    && rm -rf /var/lib/apt/lists/*

# Create working directory
WORKDIR /app

# Copy source files
COPY master_node.cpp .
COPY worker_node.cpp .
COPY common.h .
COPY CMakeLists.txt .

# Create build directory
RUN mkdir build

# Build the project
WORKDIR /app/build
RUN cmake .. && make

# Set working directory back to /app
WORKDIR /app

# Create a startup script
RUN echo '#!/bin/bash\n\
# Clean up any existing connections\n\
netstat -anp | grep :12345 | awk "{print \$7}" | cut -d"/" -f1 | xargs -r kill\n\
# Start the application\n\
exec "$@"' > /app/start.sh && \
    chmod +x /app/start.sh

# Use the startup script as entrypoint
ENTRYPOINT ["/app/start.sh"]

# Default command (will be overridden by docker-compose)
CMD ["./build/master_node"]
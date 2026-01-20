FROM gcc:latest

# Install Python and system dependencies
RUN apt-get update && apt-get install -y \
    python3 python3-pip \
    socat lsof \
    xvfb libx11-6 libxext6 libxrender1 libxi6 libxtst6 libxrandr2 \
    && rm -rf /var/lib/apt/lists/*

# Install Python packages
COPY src/VnaScanGUI/requirements.txt /tmp/requirements.txt
RUN pip3 install --no-cache-dir pyserial pytest pytest-cov && \
    pip3 install --no-cache-dir -r /tmp/requirements.txt

WORKDIR /builds

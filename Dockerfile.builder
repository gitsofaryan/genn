ARG BASE=12.2.0-devel-ubuntu22.04
FROM nvidia/cuda:${BASE} AS build

LABEL maintainer="J.C.Knight@sussex.ac.uk" \
    org.opencontainers.image.documentation="https://genn-team.github.io/" \
    org.opencontainers.image.source="https://github.com/genn-team/genn" \
    org.opencontainers.image.title="PyGeNN wheel builder"

ARG PY_VER=3.11
# Update APT database and upgrade any outdated packages and install Python, pip and swig
RUN apt-get update && \
    apt-get upgrade -y && \
    apt-get install -yq --no-install-recommends python${PY_VER}-dev python3-pip swig

# Set environment variables
ENV CUDA_PATH=/usr/local/cuda \
    GENN_PATH=/opt/genn

# Set python3 to be the dfault version of python
RUN update-alternatives --install /usr/bin/python python /usr/bin/python${PY_VER} 1

# Upgrade pip itself and install numpy
RUN python -m pip install --upgrade pip && \
    python -m pip install numpy

# Copy GeNN into /opt
COPY . ${GENN_PATH}

# Use this as working directory
WORKDIR ${GENN_PATH}

# Install GeNN and PyGeNN
RUN make install -j `lscpu -p | egrep -v '^#' | sort -u -t, -k 2,4 | wc -l`
RUN make DYNAMIC=1 LIBRARY_DIRECTORY=${GENN_PATH}/pygenn/genn_wrapper/ -j `lscpu -p | egrep -v '^#' | sort -u -t, -k 2,4 | wc -l`
# RUN python3 setup.py develop
RUN python setup.py bdist_wheel
RUN python setup.py bdist_wheel

# Copy the wheel to a new image for extraction
FROM scratch AS output
# TODO: Find a workaround for broken variable expansion
#ARG GENN_PATH
#COPY --from=build ${GENN_PATH}/dist/*.whl /
COPY --from=build /opt/genn/dist/*.whl /

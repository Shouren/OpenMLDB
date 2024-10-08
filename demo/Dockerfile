FROM openjdk:11.0.13-jre-slim-bullseye

LABEL org.opencontainers.image.source https://github.com/4paradigm/OpenMLDB

# if use provided pysdk to test, set it to true, add whl in additions/
ARG USE_ADD_WHL=""
# if use provided pkgs to test, set it to skip_download, and add pkgs in additions/
ARG SKIP_DOWNLOAD=""

# If 'apt update' is too slow, add the mirror sources file n additions/
ADD additions /additions
RUN if [ -f "/additions/sources.list" ] ; then cp /additions/sources.list /etc/apt/ ; fi

RUN  apt-get update \
     && apt-get install -y --no-install-recommends libgomp1 curl binutils procps python3 python3-pip python3-numpy vim rsync \
     && rm -rf /var/lib/apt/lists/*

RUN if [ -f "/additions/pypi.txt" ] ; then pip config set global.index-url $(cat /additions/pypi.txt) ; fi
RUN pip install --no-cache-dir py4j==0.10.9 lightgbm==3 tornado requests pandas==1.5 xgboost==1.4.2 numpy==1.26.4

COPY init.sh /work/
COPY predict-taxi-trip-duration/script /work/taxi-trip/
COPY talkingdata-adtracking-fraud-detection /work/talkingdata/
COPY *_dist.yml /work/

ENV LANG=en_US.UTF-8
ENV SPARK_HOME=/work/openmldb/spark-3.2.1-bin-openmldbspark
ARG OPENMLDB_VERSION=0.9.2
ENV OPENMLDB_VERSION="${OPENMLDB_VERSION}"

RUN  if [ "${USE_ADD_WHL}" = "true" ] ; then \
          pip install /additions/openmldb*.whl ; \
     else \
          pip install --no-cache-dir "openmldb==${OPENMLDB_VERSION}" "openmldb-tool" ; \
     fi

COPY setup_openmldb.sh /
RUN if [ "${SKIP_DOWNLOAD}" = "skip_download" ] ; then cp /additions/*gz / ; fi
RUN /setup_openmldb.sh "${OPENMLDB_VERSION}" "${SKIP_DOWNLOAD}" && rm /setup_openmldb.sh

RUN rm -r /additions

WORKDIR /work

CMD [ "/bin/bash" ]

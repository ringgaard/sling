FROM python:3.7

COPY wheel/sling-2.0.0-py3-none-linux_x86_64.whl /tmp
RUN pip install /tmp/sling-2.0.0-py3-none-linux_x86_64.whl

RUN mkdir -p /var/data/sling
WORKDIR /var/data/sling

COPY python/run.py python/run.py
COPY run.sh run.sh

CMD ["sh"]


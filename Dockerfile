FROM python:3.7-slim

WORKDIR /home/sling

COPY wheel/sling-2.0.0-py3-none-linux_x86_64.whl /tmp
COPY data data

RUN pip install /tmp/sling-2.0.0-py3-none-linux_x86_64.whl

EXPOSE 6767

CMD ["bash"]


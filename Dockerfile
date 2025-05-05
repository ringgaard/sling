FROM python:3.9-slim

WORKDIR /home/sling

COPY dist/sling-3.1.0-py3-none-linux_x86_64.whl /tmp
COPY data data

RUN pip install /tmp/sling-3.1.0-py3-none-linux_x86_64.whl

EXPOSE 6767

CMD ["bash"]

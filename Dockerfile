FROM python:3.7

COPY wheel/sling-2.0.0-py3-none-linux_x86_64.whl /tmp
RUN pip install /tmp/sling-2.0.0-py3-none-linux_x86_64.whl

WORKDIR /var/data/sling

EXPOSE 6767

CMD ["sh"]


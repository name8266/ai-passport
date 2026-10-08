FROM python:3.12-slim
ENV PYTHONUNBUFFERED=1
WORKDIR /gateway
COPY requirements.txt /gateway/requirements.txt
RUN python -m pip install --no-cache-dir -r /gateway/requirements.txt \
    && useradd -r -m -U gateway && mkdir /data && chown gateway:gateway /data
COPY app.py /gateway/app.py
USER gateway
ENV HUB_DATA_DIR=/data
EXPOSE 8765
CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "8765", "--no-access-log"]

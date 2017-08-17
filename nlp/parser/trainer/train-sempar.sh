#!/bin/bash

DATA=local/sempar

./nlp/parser/trainer/train.sh \
  --batch=6 \
  --commons=$DATA/commons \
  --train=$DATA/dev.small.gold.zip \
  --dev=$DATA/dev.small.gold.zip \
  --dev_without_gold=$DATA/dev.small.without-gold.zip \
  --output=$DATA/out \
  --oov_lstm_features=false \
  --train_steps=500 \
  --word_dim=32 \
  --pretrained_embeddings=$DATA/word2vec-embedding-bi-true-32.tf.recordio \
  --seed=2 \
  --method=adam \
  --adam_beta1=0.01 \
  --adam_beta2=0.999 \
  --adam_eps=0.00001 \
  --eta=0.0005 \
  --dropout=1.0 \
  --decay=800000 \
  --moving_average=true \
  --grad_clip_norm=1.0 \
  --report_every=100 \


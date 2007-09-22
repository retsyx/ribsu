#!/bin/sh

cp ../build/Deployment/ribsu .
strip ribsu
tar cfvz ribsu.tgz ribsu ribsu.rtf

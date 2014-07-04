#!/bin/bash

OUTPUT_FILE=summary

find . -name summary -exec egrep -H '("run_avg"|run_stddev")' {} \; | tr '/:' '\t ' > $OUTPUT_FILE


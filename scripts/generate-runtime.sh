#!/usr/bin/env sh

ID=$1
SOURCE=$2
TARGET=$3

if [ -z "$TARGET" ] ; then
  echo "ERROR: $0 <ID> <SOURCE> <TARGET>"
  exit 1
fi

echo "#ifndef CAPSULE_RUNTIME_H
#define CAPSULE_RUNTIME_H

const char *$ID = " > $TARGET

cat $SOURCE | sed 's#"#\"#g' | awk '{ printf "\"%s\\n\"\n", $0 }' >> $TARGET


echo ";
#endif" >> $TARGET
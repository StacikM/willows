#!/bin/sh
# Regenerate src/trust_anchors.h from Mozilla's CA bundle (needs curl and BearSSL's brssl tool built from the BearSSL tarball).
# usage: tools/mkcerts.sh /path/to/brssl
set -e
BRSSL=${1:?usage: $0 /path/to/brssl}
curl -fsSL https://curl.se/ca/cacert.pem -o /tmp/cacert.pem
"$BRSSL" ta /tmp/cacert.pem > src/trust_anchors.h
echo "wrote src/trust_anchors.h"

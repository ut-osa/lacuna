# Uncomment if libgcrypt performs CTR-mode encryptions like a "real
# stream cipher" (without discarding block pieces) - I believe v1.5.0
# and up
# CFLAGS += -DCONFIG_GCRYPT_REAL_STREAM

# Use aesni ops instead of libgcrypt
CFLAGS += -DUSE_AES_NI

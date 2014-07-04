obj-y += priv_crypt_drv.o crypto.o sysfs.o usb.o kex.o

obj-$(CONFIG_PRIV_CRYPT_MATH) += crypto_math.o
crypto_math-y += crypto_math_drv.o
crypto_math-y += polarssl/dhm.o polarssl/bignum.o
# TLS v1.1 pieces
crypto_math-y +=   polarssl/ssl_tls.o polarssl/sha1.o polarssl/md5.o


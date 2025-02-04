#!/bin/bash

dev_dir=dev
ttp_dir=ttp
dev_crt_conf=dev_crt.cnf
dev_csr_conf=dev_csr.cnf
ttp_crt_conf=ttp_crt.cnf

# Clean up existing directories
rm -rf rsa2048
rm -rf rsa3072
rm -rf rsa4096

if [ $# -gt 0 ]
then
    # Check if certificates generation successfully done
    if [ ! -d "rsa2048" ] || [ ! -d "rsa3072" ] || [ ! -d "rsa4096" ]; then
        echo "[Usage] generate_key_and_crt.sh"
        exit 1
    fi
    for key_size in 2048 3072 4096
    do
        echo "================================ [${key_size}] Certificate of TTP ================================"
        openssl x509 -noout -text -in rsa${key_size}/$ttp_dir/crt.pem

        echo "================================ [${key_size}] CSR of Device ================================"
        openssl req -noout -text -in rsa${key_size}/$dev_dir/csr.pem

        echo "================================ [${key_size}] Certificate of Device ================================"
        openssl x509 -noout -text -in rsa${key_size}/$dev_dir/crt.pem
    done
    exit 0
fi

for key_size in 2048 3072 4096
do
    mkdir -p rsa${key_size}/$dev_dir
    mkdir -p rsa${key_size}/$ttp_dir
    cp $dev_crt_conf rsa${key_size}/
    cp $dev_csr_conf rsa${key_size}/
    cp $ttp_crt_conf rsa${key_size}/

    # Update key size in config files if needed
    sed -i "s/default_bits = .*/default_bits = ${key_size}/g" rsa${key_size}/$dev_crt_conf
    sed -i "s/default_bits = .*/default_bits = ${key_size}/g" rsa${key_size}/$dev_csr_conf
    sed -i "s/default_bits = .*/default_bits = ${key_size}/g" rsa${key_size}/$ttp_crt_conf

    # Dev side
    openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:${key_size} -out rsa${key_size}/$dev_dir/key.pem
    openssl rsa -in rsa${key_size}/$dev_dir/key.pem -pubout > rsa${key_size}/$dev_dir/pub.pem
    openssl req -new -key rsa${key_size}/$dev_dir/key.pem -out rsa${key_size}/$dev_dir/csr.pem -config dev_csr.cnf

    # TTP side
    openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:${key_size} -out rsa${key_size}/$ttp_dir/key.pem
    openssl rsa -in rsa${key_size}/$ttp_dir/key.pem -pubout > rsa${key_size}/$ttp_dir/pub.pem
    openssl req -x509 -new -key rsa${key_size}/$ttp_dir/key.pem -out rsa${key_size}/$ttp_dir/crt.pem -days 365 -config ttp_crt.cnf

    # Sign device certificate
    openssl x509 -req -in rsa${key_size}/$dev_dir/csr.pem -out rsa${key_size}/$dev_dir/crt.pem -days 365 \
        -CA rsa${key_size}/$ttp_dir/crt.pem -CAkey rsa${key_size}/$ttp_dir/key.pem -CAcreateserial \
        -extensions req_ext -extfile dev_crt.cnf
done

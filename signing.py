import socket
from codecs import encode, decode
from mbedtls import pk
from hashlib import sha256

# Read device certificate and convert newlines to \n
with open('keys/secp256r1/dev/crt.pem', 'r') as f:
    device_cert = f.read().strip().replace('\n', '\\n')

# Read device public key and convert newlines to \n
with open('keys/secp256r1/dev/pub_2.pem', 'r') as f:
    device_pubkey = f.read().strip().replace('\n', '\\n')

# Base manifest as a raw string
manifest_content = "device_id:19682938\n" + \
    "device_status:active\n" + \
    "device_type:blinking led\n" + \
    "sensors:null\n" + \
    "actuators:led\n" + \
    "network:wifi\n" + \
    "purpose:null\n" + \
    "manufacturer:paisa\n" + \
    "full_specification_link:null\n" + \
    "user_manual_link:null\n" + \
    "location:null\n" + \
    "description:sample application, blinking led\n" + \
    "certificate_of_device:" + device_cert + "\\n\n" + \
    "pk_of_dev:" + device_pubkey + "\\n\n"

def signMessage(msg, key_file):
    ecc = pk.ECC()
    ecc = ecc.from_file(key_file)
    msg_hash = sha256(msg).digest()
    return ecc.sign(msg_hash)

# Read manufacturer certificate and convert newlines to \n
with open('keys/secp256r1/ttp/crt.pem', 'r') as f:
    manufacturer_cert = f.read().strip().replace('\n', '\\n')

# Sign the manifest
signature = signMessage(manifest_content.encode(), 'keys/secp256r1/ttp/key.pem')
signature_b64 = encode(signature, 'base64').decode().strip().replace('\n', '\\n')

# Create final output
final_output = manifest_content + \
    "signature_of_manifest:" + signature_b64 + "\n" + \
    "certificate_of_manufacturer:" + manufacturer_cert + "\\n"

# Write to manifest.txt
with open('manifest.txt', 'w') as f:
    f.write(final_output)

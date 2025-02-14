from mbedtls import pk
from hashlib import sha256
import base64

def load_public_key(key_file):
    with open(key_file, 'r') as f:
        return f.read()

def verify_manifest():
    print("\nPublic key being used:")
    with open('keys/secp256r1/ttp/pub.pem', 'r') as f:
        print(f.read())
    # Read the manifest file
    with open('manifest.txt', 'r') as f:
        manifest_content = f.read()

    # Find where manifest content ends and signature begins
    sig_marker = "signature_of_manifest:"
    sig_start = manifest_content.find(sig_marker)
    if sig_start == -1:
        print("No signature found in manifest")
        return False

    # Split the content and signature
    content_to_verify = manifest_content[:sig_start]
    sig_line = manifest_content[sig_start + len(sig_marker):].split('\n')[0]

    # Clean up the signature (remove \n)
    signature = sig_line.replace('\\n', '')
    signature = base64.b64decode(signature)

    # Calculate hash of the content
    content_hash = sha256(content_to_verify.encode()).digest()

    # Print debug info
    print("Content being verified (ends with):")
    print(content_to_verify[-100:])  # Show last 100 chars
    print("\nContent hash:")
    print(content_hash.hex())
    print("\nSignature (base64):")
    print(sig_line)
    print(len(sig_line))
    print("\nDecoded signature (hex):")
    print(signature.hex())

    # Load public key
    pub_key = pk.ECC()
    pub_key = pub_key.from_file('keys/secp256r1/ttp/pub.pem')

    # Verify signature
    try:
        pub_key.verify(content_hash, signature)
        print("\nSignature verification SUCCESS")


                # In your Python script
        print("\nVerification details:")
        print("Hash:", content_hash.hex())
        print("Signature length:", len(signature))
        print("Signature bytes:", signature.hex())
        print("Using hash algorithm: SHA256")
        verification = pub_key.verify(content_hash, signature)
        print("Verification method:", pub_key.verify.__name__)
        return True
    except Exception as e:
        print(f"\nSignature verification FAILED: {str(e)}")
        return False

if __name__ == "__main__":
    verify_manifest()

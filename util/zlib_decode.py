import zlib

compressed_data = open('bits', 'rb').read()
decompressed_data = zlib.decompress(compressed_data)
print(decompressed_data)
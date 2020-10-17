import zlib

original_data = open('data', 'rb').read()
compressed_data = zlib.compress(original_data)#, zlib.Z_BEST_COMPRESSION)
print(compressed_data)
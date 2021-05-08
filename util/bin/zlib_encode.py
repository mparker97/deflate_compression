import sys
import zlib

n = len(sys.argv)
if n != 2:
    print("USAGE: {} FILE".format(sys.argv[0]))
    exit()

original_data = open(sys.argv[1], 'rb').read()
compressed_data = zlib.compress(original_data)#, zlib.Z_BEST_COMPRESSION)
#print(compressed_data)
sys.stdout.buffer.write(compressed_data)

import sys
import zlib

n = len(sys.argv)
if n != 2:
    print("USAGE: {} FILE".format(sys.argv[0]))
    exit()

compressed_data = open(sys.argv[1], 'rb').read()
original_data = zlib.decompress(compressed_data)
#print(original_data)
sys.stdout.buffer.write(original_data)
# Prints the RGB triples of each pixel of the input image

from PIL import Image
import sys

if __name__ == "__main__":
	if len(sys.argv) != 2:
		print("Usage: {} IMAGE".format(sys.argv[0]))
	else:
		try:
			im = Image.open(sys.argv[1])
		except IOError:
			print("Failed to open image {}".format(sys.argv[1]))
			sys.exit(1)
		rgb_im = im.convert("RGB")
		for j in range(rgb_im.height):
			for i in range(rgb_im.width):
				r, g, b = rgb_im.getpixel((i, j))
				print("{} {} {}".format(r, g, b))

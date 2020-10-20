from PIL import Image
im = Image.open("image.png")
width, height = im.size
for i in range(height):
	for j in range(width):
		r, g, b = im.getpixel((i, j))
		print("{} {} {}".format(r, g, b))

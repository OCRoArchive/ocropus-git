image = intarray()
iulib.read_image_packed(image,arg[1])
graphics.dinit(image:dim(0),image:dim(1),true)
graphics.dshowr(image)
graphics.dwait()
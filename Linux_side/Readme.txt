All the files in this folder are supposed to placed in this location: "/mnt/sda1/www"

Now a folder named "www" has to be created in the "/root" location.

and a link file has to be created with a shell command "ln -s /mnt/{microsd device name}/ www/sd"

Run the arduino sketch file and open the website in a web browser with the url "http://ipaddress_of_arduino/sd/index.html"


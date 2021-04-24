import os
import shutil
import datetime

time=datetime.datetime.now()
date=time.date()
location="/mnt/sda1/www/"
format='.txt'

filename = ''.join(str(x) for x in (location, date,format))


os.system("cp /mnt/sda1/www/Sensor_data.txt " + filename)
os.remove('/mnt/sda1/www/Sensor_data.txt')
os.remove('/mnt/sda1/www/log.csv')



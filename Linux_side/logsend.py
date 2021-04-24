# coding=utf-8
#Universita' degli Studi di Modena e Reggio Emilia
#Dipartimento di Ingegneria Enzo Ferrari
#Authors= Gibertoni/Merani/Maini
#Data: 26/11/2018

import smtplib, os, sys
import datetime
import shutil


from email.MIMEMultipart import MIMEMultipart
from email.MIMEBase import MIMEBase
from email.MIMEText import MIMEText
from email.Utils import COMMASPACE, formatdate
from email import Encoders


time=datetime.datetime.now()
date=time.date()
location = "/mnt/sda1/www/"
format='.txt'
filename = ''.join(str(x) for x in (location, date,format))
filenm = ''.join(str(x) for x in (date,format))

os.system("cp /mnt/sda1/www/Sensor_data.txt " + filename)



#Sender address, Destination address, Mail subject, Mail body
from_address    = 'kummaltest@gmail.com'
to_address      = ['kummaltest@gmail.com']
email_subject   = 'Field sensor Data'
email_body      = 'Kindly find the log file attached with this email.'
files = [filename]

#files=["/mnt/sda1/www/Sensor_data.txt"]
 
# Credentials of the gmail sender
username = 'kummaltest@gmail.com'
password = 'Hei7353haod'

# sending the MAIL message
server = 'smtp.gmail.com:587'

def send_mail(send_from, send_to, subject, text, txt, server="localhost"):
    assert type(send_to)==list
    #assert type(files)==list

    msg = MIMEMultipart()
    msg['From'] = send_from
    msg['To'] = COMMASPACE.join(send_to)
    msg['Date'] = formatdate(localtime=True)
    msg['Subject'] = subject

    msg.attach( MIMEText(text) )

    for f in txt:
        part = MIMEBase('application', "octet-stream")
        part.set_payload( open(f,"rb").read() )
        Encoders.encode_base64(part)
        part.add_header('Content-Disposition', 'attachment; filename="%s"' % os.path.basename(f))
        msg.attach(part)

    smtp = smtplib.SMTP(server)
    smtp.starttls()
    smtp.login(username,password)
    smtp.sendmail(send_from, send_to, msg.as_string())
    smtp.close()

send_mail(from_address, to_address, email_subject, email_body, files, server) #the first command line argument will be used as the image file name

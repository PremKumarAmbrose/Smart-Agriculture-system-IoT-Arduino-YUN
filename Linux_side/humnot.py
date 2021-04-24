#!/usr/bin/python

import smtplib

sender = 'kummaltest@gmail.com'
receivers = 'kummaltest@gmail.com'
passwd = 'Hei7353haod'
message = 'The humidity value is out of the specified range!'


smtpObj = smtplib.SMTP('smtp.gmail.com',587)
smtpObj.ehlo()
smtpObj.starttls()
smtpObj.ehlo()
#smtpObj.set_debuglevel(0)


smtpObj.login(sender,passwd)
smtpObj.sendmail(sender, receivers, message)
smtpObj.close()
print "Successfully sent e-mail"
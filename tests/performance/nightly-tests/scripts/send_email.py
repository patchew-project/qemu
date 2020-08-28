"""
Helper script for sending emails with the nightly performance test results.

This file is a part of the project "TCG Continuous Benchmarking".

Copyright (C) 2020  Ahmed Karaman <ahmedkhaledkaraman@gmail.com>
Copyright (C) 2020  Aleksandar Markovic <aleksandar.qemu.devel@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
"""


import smtplib
from typing import List
from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText
from email.utils import COMMASPACE, formatdate


GMAIL_USER = {"name": "",
              "email": "",
              "pass": ""}


def send_email(subject: str, send_to: List[str], html: str) -> None:
    """
    Send an HTML email.

    Parameters:
    subject (str): Email subject
    send_to (List(str)): List of recipients
    html (str): HTML message
    """
    msg = MIMEMultipart('alternative')
    msg['From'] = "{} <{}>".format(GMAIL_USER["name"], GMAIL_USER["email"])
    msg['To'] = COMMASPACE.join(send_to)
    msg['Date'] = formatdate(localtime=True)
    msg['Subject'] = subject

    msg.attach(MIMEText(html, 'html'))

    server = smtplib.SMTP_SSL('smtp.gmail.com', 465)
    server.login(GMAIL_USER["email"], GMAIL_USER["pass"])
    server.sendmail(msg['From'], send_to, msg.as_string())
    server.quit()

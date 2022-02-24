# Copyright 2022 Ringgaard Research ApS
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Send alerts to SLING system monitor."""

import email.message
import email.utils
import smtplib

import sling.flags as flags

flags.define("--smtp_server",
             help="SMTP server address for receiving alerts",
             default="master.ringgaard.com",
             metavar="ADDR")

flags.define("--smtp_port",
             help="SMTP server port for receiving alerts",
             default=2525,
             type=int,
             metavar="PORT")

flags.define("--alert_recipient",
             help="alert recipient email address",
             default="SLING admin <admin@ringgaard.com>",
             metavar="EMAIL")

flags.define("--alert_sender",
             help="alert sender email address",
             default="SLING alert <alert@ringgaard.com>",
             metavar="EMAIL")

def send(subject, message, sender=None):
  # Build email message.
  mail = email.message.EmailMessage()
  mail["From"] = flags.arg.alert_sender if sender is None else sender
  mail["To"] = flags.arg.alert_recipient
  mail["Subject"] = subject
  mail["Date"] = email.utils.formatdate(localtime=True)
  mail["Message-Id"] = email.utils.make_msgid()
  mail.set_content(message)

  # Send email message.
  smtp = smtplib.SMTP(flags.arg.smtp_server, flags.arg.smtp_port)
  smtp.send_message(mail)

if __name__ == "__main__":
  flags.parse()
  send("Test message", "This is a test.")


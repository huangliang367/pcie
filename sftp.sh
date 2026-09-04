#/bin/bash

sftp -P 2222 -i ~/.ssh/id_rsa root@127.0.0.1

scp /home/hl/pcie/build/modules/demo_ep/demo_ep.ko root@127.0.0.1:/home/hl/pcie
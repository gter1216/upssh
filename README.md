# UPSSH

UPSSH is a very lightly simulated implementation of the openSSH protocol tool for remote login.


The advantage is UPSSH:

1. lightly, because UPSSH do not use secured method to protect the data.
   The overall line count not exceeds 2000.
   
2. Do not need give password of any user you want to manipulate. 


How to Build UPSSH:

    make clean && make
    
How to Usage:

    1. In server side, run "./upssh 0" with root previledge
    2. In client side, run "./upssh $USER@$HOST" with any user previledge.

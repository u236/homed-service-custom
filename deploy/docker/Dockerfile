FROM    127.0.0.1:5000/homed-service:latest
ARG     TARGETARCH
RUN     mkdir /etc/homed
RUN     ln -s /data/homed-custom.conf /etc/homed/homed-custom.conf
RUN     ln -s /data/homed.log /var/log/homed.log
ADD     share /usr/share/homed-common
COPY    homed-custom-$TARGETARCH /usr/bin/homed-custom
CMD     ["/usr/bin/homed-custom"]

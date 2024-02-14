FROM jaegertracing/all-in-one@sha256:5ab99ae159c82d3333bec0b6cc0eb3c3f35b8a191d505fab63a4a8cfa74caa04
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=60 \
    CMD wget -q --header='Content-Type:application/json' -O - http://localhost:14269/health | grep "Server available"

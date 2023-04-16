### Test Sender side command:
`mm-delay 5 mm-loss uplink 0.2 mm-loss downlink 0.5`
`./obj/rdt_sender $MAHIMAHI_BASE 5454 sample.txt`

### Test Receiver side command:
`./obj/rdt_receiver 5454 receiver.txt`

<hr />

You can test using these network conditions (run sender in these conditions) and files:

### Test 1
**Network condition:** mm-delay 30 mm-loss downlink 0.6
**Transfer file:** numbers.txt (4.5 MB)

### Test 2
**Network condition:** mm-delay 50 mm-loss uplink 0.6
**Transfer file:** file.pdf (1 MB)

### Test 3
**Network condition:** mm-delay 20 mm-link --meter-all ./channel_traces/cellular ./channel_traces/rapid
**Transfer file:** file.mp4 (17.8 MB)

### In all of tests:
- Check your code doesn't crash while transferring
- Check files are not corrupt
- Check files have the same content
- Check files are of same sizes (check bytes in file properties)
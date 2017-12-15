while true
do
	COUNTER=1
	while [ "$COUNTER" -lt 6 ]; do
		echo "COUNTER is $COUNTER"
		COUNTER=$(($COUNTER+1))
		stress -c 1 -i $COUNTER --verbose --timeout $COUNTER
		stress -c 2 -i $COUNTER --verbose --timeout $COUNTER
		sleep 1
	done
done


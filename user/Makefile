all:	
	make user test1 test2 test3 test4 test5 test6
user:
	gcc user.c inout.c -lpthread -o user
clean:
	rm -f user test?
test1:
	gcc test.c -lpthread -o test1 -DTEST_1
test2:
	gcc test.c -lpthread -o test2 -DTEST_2
test3:
	gcc test.c -lpthread -o test3 -DTEST_3
test4:
	gcc test.c -lpthread -o test4 -DTEST_4
test5:
	gcc test.c -lpthread -o test5 -DTEST_5
test6:
	gcc test.c -lpthread -o test6 -DTEST_6

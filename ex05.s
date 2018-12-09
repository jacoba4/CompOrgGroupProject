add $t0,$zero,66
add	$t1,$zero,66
add $t2,$zero,$zero
abc:
add $t2,$t2,1
beq $t0,$t1,abc

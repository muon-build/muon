#ifndef MAGIC
#error "magic not defined"
#endif

int deflate(void *, int);


int
main(void)
{
	return deflate(0, 0);
}

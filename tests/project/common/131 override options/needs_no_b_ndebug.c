int
main(void)
{
#ifdef NDEBUG
	return 1;
#else
	return 0;
#endif
}

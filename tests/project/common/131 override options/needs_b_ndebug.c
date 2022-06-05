int
main(void)
{
#ifdef NDEBUG
	return 0;
#else
	return 1;
#endif
}

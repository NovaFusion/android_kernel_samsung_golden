#ifndef _PX3315_h_
#define	_PX3315_h_


#define	PX3315C_DEV_NAME	"px3315"

struct px3315_platform_data
{
	unsigned int	ps_vout_gpio;
	int (* hw_setup)( void );
};

#endif

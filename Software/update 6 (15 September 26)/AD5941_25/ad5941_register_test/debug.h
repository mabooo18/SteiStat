#define	DEBUG 	0
#define	MEASURE	0

extern void Log(uint32_t mask, int lineNum, const char* format, ...);
extern void Info(uint32_t mask, const char* format, ...);

#if DEBUG
extern void PrintTime(long time);
extern char Pipes[];
extern int Indentation;
extern long loopStartTime;

	#define ENTER(FunctionName)		long __entry__ = millis();			\
									const char __functionName__[] = FunctionName; \
									Serial.print(Pipes + Indentation);	\
									Serial.print("/- ");				\
									Serial.print(FunctionName);			\
									Serial.print(" ms=");				\
									Serial.println(__entry__ - loopStartTime); \
									Indentation -= 2;
	#define LEAVE
	#define RETURN(Value)		return Value;
	#define	PRINTVAR(Name, Value)
	#define	PRINTVARHEX(Name, Value)
	#define	PRINT2VARHEX(Value1, Value2)
	#define	PRINTVARBIN(Name, Value)
	#define	SETVALUE(Name, Expression)	(Expression);
	#define	PRINTLINENUMBER
#else
	#define ENTER(FunctionName)
	#define LEAVE
	#define RETURN(Value)				return Value;
	#define	PRINTVAR(Name, Value)
	#define	PRINTVARHEX(Name, Value)
	#define	PRINT2VARHEX(Value1, Value2)
	#define	PRINTVARBIN(Name, Value)
	#define	SETVALUE(Name, Expression)	(Expression);
	#define	PRINTLINENUMBER
#endif

#define	LED_ON	LOW
#define	LED_OFF	HIGH
extern void LightLed(bool red, bool green, bool blue);
extern void BlinkLed();
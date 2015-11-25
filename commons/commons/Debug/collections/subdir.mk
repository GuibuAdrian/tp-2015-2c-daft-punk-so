################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../collections/dictionary.c \
../collections/list.c \
../collections/queue.c 

OBJS += \
./collections/dictionary.o \
./collections/list.o \
./collections/queue.o 

C_DEPS += \
./collections/dictionary.d \
./collections/list.d \
./collections/queue.d 


# Each subdirectory must supply rules for building sources it contributes
collections/%.o: ../collections/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '



################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../bitarray.c \
../config.c \
../error.c \
../log.c \
../process.c \
../string.c \
../temporal.c \
../txt.c 

OBJS += \
./bitarray.o \
./config.o \
./error.o \
./log.o \
./process.o \
./string.o \
./temporal.o \
./txt.o 

C_DEPS += \
./bitarray.d \
./config.d \
./error.d \
./log.d \
./process.d \
./string.d \
./temporal.d \
./txt.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '



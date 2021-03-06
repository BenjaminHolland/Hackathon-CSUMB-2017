﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Reactive;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Reactive.Threading.Tasks;
using Nito.AsyncEx;
using RJCP.IO.Ports;
using System.Reactive.Subjects;
namespace IoT_Controller
{
    public static class SerialPortStreamExt
    {
        //Sends a query to the device to report it's current light reading.
        public static async Task SendLightQueryPacketAsync(this SerialPortStream stream)
        {
            byte[] buffer = new byte[3];

            buffer[0] = 255;
            buffer[1] = 0;
            buffer[2] = 0;

            char[] outA = new char[10];
            string encoded_packet=Convert.ToBase64String(buffer);
            
            encoded_packet += '\n';
            
            buffer = Encoding.ASCII.GetBytes(encoded_packet);
            
            await stream.WriteAsync(buffer, 0, buffer.Length).ConfigureAwait(false);
            //Console.WriteLine(encoded_packet);
        }

        /// <summary>
        /// Observe this serial ports data as a stream of byte arrays.
        /// </summary>
        /// 
        /// <param name="stream"></param>
        /// <returns></returns>
        public static IObservable<byte[]> ObserveBytes(this SerialPortStream stream)
        {
            byte[] buffer = new byte[4096];
            return Observable.Create<byte[]>(async obs => {
                while (stream.IsOpen) {
                    int rx = await stream.ReadAsync(buffer, 0, 4096).ConfigureAwait(false);
                    //Console.WriteLine($"Got {rx} bytes");
                    if (rx != 0) {
                        obs.OnNext(buffer.Take(rx).ToArray());
                    }
                }
            });
        }
    }
    class Program
    {
        /// <summary>
        /// Takes a packet and decodes it.
        /// </summary>
        /// <param name="bytes"></param>
        /// <returns></returns>
        static byte[] DecodePacket(IList<byte> bytes)
        {
            try {
                byte[] packet = Convert.FromBase64String(Encoding.ASCII.GetString(bytes.ToArray()));
                return packet;
            } catch (Exception ex) {
                return null;
            }
        }

        static async Task MainAsync(string[] args)
        {
            using (SerialPortStream stream = new SerialPortStream("COM5")) {
                stream.BaudRate = 115200;
                stream.Open();

                //Convert the stream of chuncks to a nice, smooth byte stream.
                var byteSource = stream.ObserveBytes()
                    .SelectMany(bytes => bytes)
                    .ObserveOn(NewThreadScheduler.Default)
                    .Publish();

                //filter out everthing but the separator characters.
                var newlineSource = byteSource
                    .Where(value => value == '\n');

                //split stream into buffers on the separator character
                var packetSource = byteSource
                    .Buffer(newlineSource)
                    .Select(bytes => DecodePacket(bytes))
                    .Where(packet => packet != null);

                //Set handler for integer packets
                var lightStream = packetSource
                    .Where(packet => packet[1] == 1)
                    .Select(packet => BitConverter.ToInt16(packet, 3))
                    .Multicast(new Subject<short>());

                lightStream.Connect();

                var lowLightSub = lightStream
                    .Where(value => value < 50)
                    .ObserveOn(NewThreadScheduler.Default)
                    .Subscribe(packet => {
                        Console.WriteLine("Low Light");
                    });

                var hiLightSub = lightStream
                    .Where(value => value > 800)
                    .ObserveOn(NewThreadScheduler.Default)
                    .Subscribe(packet => {
                        Console.WriteLine("High Light");
                    });

                var kickoffSub = lightStream.Subscribe(value => {
                    stream.SendLightQueryPacketAsync().ToObservable().Wait(); ;
                });

                //set handler for string packets.
                var stringSub = packetSource
                    .Where(packet => packet[1] == 0)
                    .ObserveOn(NewThreadScheduler.Default)
                    .Subscribe(packet => Console.WriteLine(Encoding.ASCII.GetString(packet, 3, packet[1])));
                

                //Kick off the call/response pattern
                await stream.SendLightQueryPacketAsync();
                using (byteSource.Connect()) {
                    await byteSource;
                }
            }
        }

        static void Main(string[] args)
        {
            AsyncContext.Run(() => MainAsync(args));   
        }
    }
}

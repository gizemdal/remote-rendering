using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Networking;
using System.IO;
using MLAPI;
using MLAPI.Messaging;
using UnityEngine.UI;
using System;
using UnityEngine.Events;
using System.Threading;
using System.Threading.Tasks;

public class SendImage : NetworkedBehaviour
{
    private static readonly string LOG_PREFIX = "[" + typeof(SendImage).Name + "]: ";
    private static int defaultBufferSize = 256; //max ethernet MTU is ~1400

    [SerializeField] Texture2D m_receivedTexture;
    Dictionary<int, PPmToTexture2d> m_textures;
    private class TransmissionData
    {
        public int curDataIndex; //current position in the array of data already received.
        public byte[] data;

        public TransmissionData(byte[] _data)
        {
            curDataIndex = 0;
            data = _data;
        }
    }

    public event UnityAction<int, byte[]> OnDataComepletelySent;
    public event UnityAction<int, byte[]> OnDataFragmentSent;
    public event UnityAction<int, byte[]> OnDataFragmentReceived;
    public event UnityAction<int, byte[]> OnDataCompletelyReceived;


    List<int> serverTransmissionIds = new List<int>();

    //maps the transmission id to the data being received.
    Dictionary<int, TransmissionData> clientTransmissionData = new Dictionary<int, TransmissionData>();


    void Start()
    {
        m_textures = new Dictionary<int, PPmToTexture2d>();
        PPmToTexture2d[] _textures = GetComponentsInChildren<PPmToTexture2d>();
        InitTextureDiction(_textures);
        OnDataFragmentReceived += MyFragmentReceivedHandler;
        OnDataCompletelyReceived += MyCompletelyReceivedHandler;

        
    }

    void InitTextureDiction(PPmToTexture2d[] _textures)
    {
        for(int i=0;i< _textures.Length;i++)
        {
            m_textures[_textures[i].count] = _textures[i];
        }
    }

    // Update is called once per frame
    void Update()
    {
        if (Input.GetKeyDown(KeyCode.T))
        {
            if (IsServer)
            {
                var dirPath = Application.dataPath;
                //Debug.Log("Sending texture" + m_texture.EncodeToPNG().Length);
                //byte[] file = File.ReadAllBytes("C:/Users/tpura/Desktop/CIS 565 GPU/remote-rendering/optix_sdk_7_2_0/SDK/build/optixPathTracer/oImage.png");
                //StartCoroutine(SendBytesToClientsRoutine(1, file));

                // InvokeClientRpcOnEveryone(RPCClientSendImage, m_texture.EncodeToPNG() );
                SendDataSequentiially();
                
            }
        }
    }


    void SendDataSequentiially()
    {
        foreach (var item in m_textures)
        {
            byte[] imageArray = item.Value.m_textureToSend.EncodeToPNG();
            if (imageArray != null)
            {
                Debug.Log("Sending Non Null Data");
                //StartCoroutine(SendBytesToClientsRoutine(item.Key, imageArray));
                SendBytesToClientsRoutineThread(item.Key, imageArray);
            }
        }
    }


    //[ClientRPC]
    //void RPCClientSendImage(byte[] texturebytes)
    //{
    //    m_receivedTexture = new Texture2D(1, 1);
    //    m_receivedTexture.LoadImage(texturebytes);
    //    ShowTexture.GetComponent<Renderer>().material.mainTexture = m_receivedTexture;
    //    var dirPath = Application.dataPath;
    //    File.WriteAllBytes(dirPath + "/Image1.PNG", texturebytes);
    //    Debug.Log("TextureWritten at "+ dirPath);
    //}

    public byte[] Combine(byte[] first, byte[] second)
    {
        byte[] bytes = new byte[first.Length + second.Length];
        Buffer.BlockCopy(first, 0, bytes, 0, first.Length);
        Buffer.BlockCopy(second, 0, bytes, first.Length, second.Length);
        return bytes;
    }

    Task SendBytesToClientsRoutineThread(int transmissionId, byte[] data)
    {
        Debug.Assert(!serverTransmissionIds.Contains(transmissionId));
        Debug.Log(LOG_PREFIX + "SendBytesToClients processId=" + transmissionId + " | datasize=" + data.Length);

        //tell client that he is going to receive some data and tell him how much it will be.
        InvokeClientRpcOnEveryone(RpcPrepareToReceiveBytes, transmissionId, data.Length);
        Task.Delay(10);
        //begin transmission of data. send chunks of 'bufferSize' until completely transmitted.
        serverTransmissionIds.Add(transmissionId);
        TransmissionData dataToTransmit = new TransmissionData(data);
        int bufferSize = defaultBufferSize;
        while (dataToTransmit.curDataIndex < dataToTransmit.data.Length - 1)
        {
            //determine the remaining amount of bytes, still need to be sent.
            int remaining = dataToTransmit.data.Length - dataToTransmit.curDataIndex;
            if (remaining < bufferSize)
                bufferSize = remaining;

            //prepare the chunk of data which will be sent in this iteration
            byte[] buffer = new byte[bufferSize];
            System.Array.Copy(dataToTransmit.data, dataToTransmit.curDataIndex, buffer, 0, bufferSize);

            //send the chunk
            InvokeClientRpcOnEveryone(RpcReceiveBytes, transmissionId, buffer);
            dataToTransmit.curDataIndex += bufferSize;

            if (null != OnDataFragmentSent)
                OnDataFragmentSent.Invoke(transmissionId, buffer);
        }

        //transmission complete.
        serverTransmissionIds.Remove(transmissionId);

        if (null != OnDataComepletelySent)
            OnDataComepletelySent.Invoke(transmissionId, dataToTransmit.data);
        return Task.CompletedTask;
    }

    public IEnumerator SendBytesToClientsRoutine(int transmissionId, byte[] data)
    {
        Debug.Assert(!serverTransmissionIds.Contains(transmissionId));
        Debug.Log(LOG_PREFIX + "SendBytesToClients processId=" + transmissionId + " | datasize=" + data.Length);

        //tell client that he is going to receive some data and tell him how much it will be.
        InvokeClientRpcOnEveryone( RpcPrepareToReceiveBytes, transmissionId, data.Length);
        yield return null;

        //begin transmission of data. send chunks of 'bufferSize' until completely transmitted.
        serverTransmissionIds.Add(transmissionId);
        TransmissionData dataToTransmit = new TransmissionData(data);
        int bufferSize = defaultBufferSize;
        while (dataToTransmit.curDataIndex < dataToTransmit.data.Length - 1)
        {
            //determine the remaining amount of bytes, still need to be sent.
            int remaining = dataToTransmit.data.Length - dataToTransmit.curDataIndex;
            if (remaining < bufferSize)
                bufferSize = remaining;

            //prepare the chunk of data which will be sent in this iteration
            byte[] buffer = new byte[bufferSize];
            System.Array.Copy(dataToTransmit.data, dataToTransmit.curDataIndex, buffer, 0, bufferSize);

            //send the chunk
            InvokeClientRpcOnEveryone( RpcReceiveBytes,transmissionId, buffer);
            dataToTransmit.curDataIndex += bufferSize;

            yield return null;

            if (null != OnDataFragmentSent)
                OnDataFragmentSent.Invoke(transmissionId, buffer);
        }

        //transmission complete.
        serverTransmissionIds.Remove(transmissionId);

        if (null != OnDataComepletelySent)
            OnDataComepletelySent.Invoke(transmissionId, dataToTransmit.data);
    }

    [ClientRPC]
    private void RpcPrepareToReceiveBytes(int transmissionId, int expectedSize)
    {
        if (clientTransmissionData.ContainsKey(transmissionId))
            return;

        //prepare data array which will be filled chunk by chunk by the received data
        TransmissionData receivingData = new TransmissionData(new byte[expectedSize]);
        clientTransmissionData.Add(transmissionId, receivingData);
    }
    [ClientRPC]
    private void RpcReceiveBytes(int transmissionId, byte[] recBuffer)
    {
        //already completely received or not prepared?
        if (!clientTransmissionData.ContainsKey(transmissionId))
            return;

        //copy received data into prepared array and remember current dataposition
        TransmissionData dataToReceive = clientTransmissionData[transmissionId];
        System.Array.Copy(recBuffer, 0, dataToReceive.data, dataToReceive.curDataIndex, recBuffer.Length);
        dataToReceive.curDataIndex += recBuffer.Length;

        if (null != OnDataFragmentReceived)
            OnDataFragmentReceived(transmissionId, recBuffer);

        if (dataToReceive.curDataIndex < dataToReceive.data.Length - 1)
            //current data not completely received
            return;

        //current data completely received
        Debug.Log(LOG_PREFIX + "Completely Received Data at transmissionId=" + transmissionId);
        clientTransmissionData.Remove(transmissionId);

        if (null != OnDataCompletelyReceived)
            OnDataCompletelyReceived.Invoke(transmissionId, dataToReceive.data);
    }

    // Since this Function Only Runs on client
    private void MyCompletelyReceivedHandler(int transmissionId, byte[] texturebytes)
    {
        m_receivedTexture = new Texture2D(1, 1);
        m_receivedTexture.LoadImage(texturebytes);
        m_textures[transmissionId].m_myRenderer.material.mainTexture = m_receivedTexture;
        //var dirPath = Application.dataPath;
        //File.WriteAllBytes(dirPath + "/Image1.PNG", texturebytes);
        //Debug.Log("TextureWritten at " + dirPath);
    }


    private void MyFragmentReceivedHandler(int transmissionId, byte[] data)
    {
        //update a progress bar or do something else with the information
    }
}

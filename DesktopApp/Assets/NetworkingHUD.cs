using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using MLAPI.Transports;
using MLAPI.Transports.UNET;
using UnityEngine.UI;
using MLAPI;

public class NetworkingHUD : MonoBehaviour
{
    [SerializeField] UnetTransport m_mlapiTransport;
    [SerializeField] InputField m_connectAddressField;
    
 
    public void StartHost()
    {
        m_connectAddressField.text = m_mlapiTransport.ConnectAddress;
        NetworkingManager.Singleton.StartHost();
    }

    public void StartClient()
    {
        if (!string.IsNullOrEmpty(m_connectAddressField.text))
        {
            m_mlapiTransport.ConnectAddress = m_connectAddressField.text;
            NetworkingManager.Singleton.StartClient();
        }
    }
}
